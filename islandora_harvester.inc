<?php

/**
 * Function: get_collections_as_array
 *
 * Description: Returns an associative array of all collection objects in Fedora instance
 *
 * @return array
 */
function get_collections_as_array() {
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  $query = 'select $object $title from <#ri>
                            where ($object <dc:title> $title
                              and $object <info:fedora/fedora-system:def/model#hasModel> <info:fedora/islandora:collectionCModel>
                              and $object <fedora-model:state> <info:fedora/fedora-system:def/model#Active>)
                            order by $title';
  $url = variable_get('fedora_repository_url', 'http://localhost:8080/fedora/risearch');
  $url .= "?type=tuples&flush=TRUE&format=csv&limit=1000&lang=itql&stream=on&query=";
  $content = do_curl($url . htmlentities(urlencode($query)));
  $list = explode("\n", $content);
  array_shift($list);
  $list = preg_replace('/info:fedora\//', '', $list);
  $options = array();
  foreach ($list as $item) {   //removes  blanks
    if ($item) {
      $parts = explode(',', $item);
      $options[$parts[0]] = $parts[1];
    }
  }
  unset($options['islandora:ContentModelCollection']);
  return $options;
}

/**
 * Function: oai_get_content_models_as_array
 *
 * Description: Returns an associative array of all collection objects in Fedora instance
 *
 * @return array
 */
function oai_get_content_models_as_array() {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  $query = 'select $object $title from <#ri>
          where ($object <dc:title> $title
          and ($object <fedora-model:hasModel> <info:fedora/fedora-system:ContentModel-3.0>
          or $object <fedora-rels-ext:isMemberOfCollection> <info:fedora/islandora:ContentModelsCollection>)
          and $object <fedora-model:state> <info:fedora/fedora-system:def/model#Active>)
          order by $title';

  $url = variable_get('fedora_repository_url', 'http://localhost:8080/fedora/risearch');
  $url .= "?type=tuples&flush=TRUE&format=csv&limit=1000&lang=itql&stream=on&query=";
  $content = do_curl($url . htmlentities(urlencode($query)));
  $list = explode("\n", $content);
  array_shift($list);
  $list = preg_replace('/info:fedora\//', '', $list);
  foreach ($list as $item) {   //removes  blanks
    if ($item) {
      $parts = explode(',', $item);
      $item = new fedora_item($parts[0]);
      if ($item->get_datastream_dissemination('ISLANDORACM')) {
        $options[$parts[0]] = $parts[1];
      }
    }
  }

  return $options;
}

/**
 * Function oai_get_data
 *
 * Description: retrieves record data from supplied URL
 *
 * @param URL $url
 * @return array
 */
function oai_get_data($url) {
  set_time_limit(0);
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  $data = do_curl($url);
  $dom = new DOMDocument();

  $dom->loadXML($data);
  $allData = array();
  $records = $dom->getElementsByTagName("record");

  foreach ($records as $record) {
    $recordData = array();
    $metadata = $record->getElementsByTagName('metadata')->item(0);
    $oai_dc = $metadata->getElementsByTagNameNS('http://www.openarchives.org/OAI/2.0/oai_dc/', '*');
    $oai_dc = $oai_dc->item(0);
    $recordData['dc'] = $dom->saveXML($oai_dc);
    $header = $record->getElementsByTagName('header')->item(0);
    $recordData['header'] = $dom->saveXML($header);
    $allData[] = $recordData;
  }
  return $allData;
}

/**
 * Function: build_oai_objects
 *
 * Description: Builds and ingests Fedora objects from OAI inputs
 *
 * @param array $record
 * @param string $namespace
 * @param string $collection_pid
 */
function build_oai_objects($record, $namespace, $collection_pid, $content_model) {
  set_time_limit(0);
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $thumbnail_file = drupal_get_path('module', 'islandora_harvester') . '/images/Crystal_Clear_action_filenew.png';

  $pid = fedora_item::get_next_PID_in_namespace($namespace);
  $dcStream = $record['dc'];
  
  /**
   * @TODO validate here?
   */
  $dc_doc = new DOMDocument();
  if (!$dc_doc->loadXML($dcStream)) {
    drupal_set_message('The document was not well-formed.','error');
    return FALSE;
  };
  if (!$dc_doc->schemaValidate()) {
    drupal_set_message('The document was not valid.','error');
    return FALSE;
  }
  
  $modsStream = oai_create_mods($dcStream);
  preg_match('/<dc:title>.*<\/dc:title>/', $dcStream, $matches);
  $title = preg_replace('/<\/?dc:title>/', "", $matches[0]);
  $item = fedora_item::ingest_new_item($pid, 'A', $title, 'Owner');
  $item->modify_datastream_by_value($dcStream, 'DC', "Dublin Core", 'text/xml');
  $item->add_datastream_from_string($modsStream, 'MODS', "MODS", 'text/xml');
  $item->add_relationship('isMemberOfCollection', $collection_pid);
  $item->add_relationship('hasModel', $content_model, 'info:fedora/fedora-system:def/model#');
  $item->add_datastream_from_file($thumbnail_file, 'TN');
}

/**
 * Function: build_mods_objects
 *
 * Description: Builds and ingests Fedora objects from OAI inputs
 *
 * @param array $record
 * @param string $namespace
 * @param string $collection_pid
 * @param string $content_model
 */
function build_mods_objects($record, $namespace, $collection_pid, $content_model) {
  set_time_limit(0);
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'BatchIngest');
  $thumbnail_file = drupal_get_path('module', 'islandora_harvester') . '/images/Crystal_Clear_action_filenew.png';

  $pid = fedora_item::get_next_PID_in_namespace($namespace);
  $modsStream = $record['mods'];
  
  /**
   * @TODO validate here?
   */
  $dcStream = batch_create_dc_from_mods($modsStream);
  preg_match('/<mods:title>(.*)<\/mods:title>/', $modsStream, $matches);
  $title = $matches[1];
  $item = fedora_item::ingest_new_item($pid, 'A', $title, 'Owner');
  $item->modify_datastream_by_value($dcStream, 'DC', "Dublin Core", 'text/xml');
  $item->add_datastream_from_string($modsStream, 'MODS', "MODS", 'text/xml');
  $item->add_relationship('isMemberOfCollection', $collection_pid);
  $item->add_relationship('hasModel', $content_model, 'info:fedora/fedora-system:def/model#');
  $item->add_datastream_from_file($thumbnail_file, 'TN');
}

/**
 * Function: build_csv_objects from CSV inputs
 *
 * Description: Builds and ingests Fedora objects
 *
 * @param array $headers
 * @param array $data
 * @param string $namespace
 * @param string $collection_pid
 */
function build_csv_objects($headers, $data, $namespace, $collection_pid, $content_model) {
  set_time_limit(0);
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'api/dublin_core');
  $thumbnail_file = drupal_get_path('module', 'islandora_harvester') . '/images/Crystal_Clear_action_filenew.png';
  $dc = new Dublin_Core();
  $limit = count($headers);
  for ($count = 0; $count < $limit; $count++) {
    if (preg_match('/dc\./', $headers[$count]) && $data[$count]) {
      $header = str_replace('dc.', 'dc:', $headers[$count]);
      $dc->add_element($header, $data[$count]);
      if (!$title && $header == "dc:title") {
        $title = $data[$count];
      }
    }
  }
  $pid = fedora_item::get_next_PID_in_namespace($namespace);
  $item = fedora_item::ingest_new_item($pid, 'A', $title, 'Owner');
  $item->add_relationship('isMemberOf', $collection_pid);
  $item->add_relationship('hasModel', $content_model, 'info:fedora/fedora-system:def/model#');
  $item->modify_datastream_by_value($dc->as_xml(), 'DC', 'Dublin Core Metadata', 'text/xml', 'X');
  $modsStream = oai_create_mods($dc->as_xml());
  $item->add_datastream_from_string($modsStream, 'MODS', "MODS", 'text/xml');
  $item->add_datastream_from_file($thumbnail_file, 'TN');
}

/**
 * Function create_marcxml_batch
 *
 * @param string $data
 * @param string $namespace
 * @param string $collection_pid
 */
function create_marcxml_batch($file,$namespace,$collection_pid, $content_model) {
  // transform to mods
  $xsl = new DOMDocument();
  $xsl->load($path . '/xslt/MARC21slim2MODS3-4.xsl');
  $input = new DOMDocument();
  $input->load($file);
  $xsl = $proc->importStylesheet($xsl);
  $newdom = $proc->transformToDoc($input);
  $content = $newdom->saveXML();
  // create a new temp file
  $filepath = file_directory_temp() .'/marc-mods-'. time();
  $mods_file = file_save_data($content,$filepath);
  
  // @TODO validate the file
  
  // send it to the MODS function
  create_mods_batch($mods_file,$namespace,$collection_pid, $content_model);
}

/**
 * Function create_ris_batch
 *
 * @param string $data
 * @param string $namespace
 * @param string $collection_pid
 */
function create_ris_batch($file,$namespace,$collection_pid, $content_model) {
  // transform to mods
  if ($mods = bibutils_ris_file_to_mods_document($file->filepath)) {
    // create a new temp file
    $content = $mods->saveXML();
    $filepath = file_directory_temp() .'/ris-mods-'. time();
    $mods_file = file_save_data($content,$filepath); 
    $file = new stdClass();
    $file->filepath = $mods_file;   
    // send it to the MODS function
    create_mods_batch($file,$namespace,$collection_pid,$content_model);
  }
}


/**
 * Function create_oai_batch
 *
 * Description: builds and kicks off oai batch
 *
 * @param string $namespace
 * @param string $collection_pid
 * @param array $urls
 */
function create_oai_batch($urls, $namespace, $collection_pid, $content_model) {

  $batch = array(
    'title' => "Creating batches of objects in Fedora repository",
    'progress_message' => t('Processed @current batches out of @total.'),
    'operations' => array(),
    'file' => drupal_get_path('module', 'islandora_harvester') . '/islandora_harvester.inc',
  );

  foreach ($urls as $url) {
    $batch['operations'][] = array('process_input', array($url, $namespace, $collection_pid, $content_model));
  }

  batch_set($batch);
}

/**
 * Function create_csv_batch
 *
 * Description: builds and kicks off csv batch
 *
 * @param File $file
 * @param string $namespace
 * @param string $collection_pid
 */
function create_csv_batch($file, $namespace, $collection_pid, $content_model) {
  set_time_limit(0);


  $csv = $file->filepath;
  $handle = fopen($csv, "r");
  $count = 1;
  $batch = array(
    'title' => t('Creating and ingesting Fedora objects)'),
    'operations' => array(),
    'file' => drupal_get_path('module', 'oai_module') . '/islandora_harvester.inc',
  );
  while (($data = fgetcsv($handle, 5000, ",")) !== FALSE) {
    if ($count == 1) {
      foreach ($data as $header) {
        $header = preg_replace('/\[.*\]/', '', $header);
        if ($pos = strpos($header, '.', 4)) {
          $header = substr($header, 0, $pos);
        }
        $headers[] = $header;
      }
    }
    else {
      $batch['operations'][] = array('build_csv_objects', array($headers, $data, $namespace, $collection_pid));
    }
    $count++;
  }
  batch_set($batch);
  return;
}

/**
 * Function create_mods_batch
 * 
 * Description: Builds a series of objects from a MODS collection document
 * @param File $file
 * @param string $namespace
 * @param string $collection_pid
 */
function create_mods_batch($file, $namespace, $collection_pid, $content_model) {
  set_time_limit(0);
  $batch = array(
    'title' => "Creating batches of objects in Fedora repository",
    'progress_message' => t('Processed @current batches out of @total.'),
    'operations' => array(),
    'file' => drupal_get_path('module', 'islandora_harvester') . '/islandora_harvester.inc',
  );

  $mods_ns = 'http://www.loc.gov/mods/v3';

  $dom = new DOMDocument();
  $success = $dom->load($file->filepath);
  if (!$success) {
    drupal_set_message('The document was not well-formed.','error');
    return FALSE;
  }
  $xsd = 'http://www.loc.gov/standards/mods/v3/mods-3-4.xsd';
  if (!$dom->schemaValidate($xsd)) {
    drupal_set_message('The document was not valid','error');
    return FALSE;
  }
  $records = array();
  foreach($dom->getElementsByTagNameNS($mods_ns,'mods') as $element) {
    $new_document = new DOMDocument('1.0');
    $node = $new_document->importNode($element,TRUE);
    $new_document->appendChild($node);
    $records[] = array('mods' => $new_document->saveXML());
  }

  foreach ($records as $record) {
    $batch['operations'][] = array('build_mods_objects',array($record, $namespace, $collection_pid, $content_model));
  }

  batch_set($batch);

}

/**
 * Function process_mods_xml
 *
 * Description: Loops through urls to create batches of Fedora objects
 * @param URL $url
 * @param string $namespace
 * @param string $collection_pid
 */
function process_mods_xml($url, $namespace, $collection_pid, $content_model) {
  set_time_limit(0);
  $records = mods_get_data($url);
}

/**
 * Function process_input
 *
 * Description: Loops through urls to create batches of Fedora objects
 * @param URL $url
 * @param string $namespace
 * @param string $collection_pid
 */
function process_input($url, $namespace, $collection_pid, $content_model) {
  set_time_limit(0);
  $records = oai_get_data($url);
  foreach ($records as $record) {
    build_oai_objects($record, $namespace, $collection_pid, $content_model);
  }
}

/**
 * Function get tokens
 *
 * Description: returns list of redemption tokens from original url
 * @param URL $url
 * @return string
 */
function get_tokens($url) {
  set_time_limit(0);
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  $urlParts = explode('?', $url);
  $base_url = $urlParts[0] . "?verb=ListRecords&resumptionToken=";

  $ch = curl_init();
  $user_agent = "Mozilla/4.0 pp(compatible; MSIE 5.01; Windows NT 5.0)";
  curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, FALSE);
  curl_setopt($ch, CURLOPT_SSL_VERIFYHOST, FALSE);
  curl_setopt($ch, CURLOPT_FAILONERROR, TRUE); // Fail on errors
  curl_setopt($ch, CURLOPT_FOLLOWLOCATION, 1); // allow redirects
  curl_setopt($ch, CURLOPT_TIMEOUT, 90); // times out after 90s
  curl_setopt($ch, CURLOPT_USERAGENT, $user_agent);
  curl_setopt($ch, CURLOPT_RETURNTRANSFER, TRUE); // return into a variable
  curl_setopt($ch, CURLOPT_URL, $url);
  $data = curl_exec($ch);
  if (!$data) {
    drupal_set_message("No results returned - please check url and login credentials", 'warning');
    return;
  }

  $dom = new DOMDocument();
  $dom->loadXML($data);
  $resumption_node = $dom->getElementsByTagName('resumptionToken')->item(0);
  $urls = array($url);
  $token = $resumption_node->nodeValue;
  while ($token) {
    $new_url = $base_url . $token;
    $urls[] = $new_url;
    $data = do_curl($new_url);
    $dom = new DOMDocument();
    $dom->loadXML($data);
    $resumption_node = $dom->getElementsByTagName('resumptionToken')->item(0);
    $token = $resumption_node->nodeValue;
  }
  return $urls;
}

/**
 * Function oai_create_mods
 *
 * Description: Creates MODS stream DC stream
 * @param string $xmlstr
 * @return string
 */
function oai_create_mods($xmlstr) {
  $path = drupal_get_path('module', 'islandora_harvester');
  module_load_include('inc', 'fedora_repository', 'api/ObjectHelper');
  module_load_include('inc', 'fedora_repository', 'core/CollectionClass');

  if ($xmlstr == NULL || strlen($xmlstr) < 5) {
    return " ";
  }

  try {
    $proc = new XsltProcessor();
  } catch (Exception $e) {
    drupal_set_message(t('@e', array('@e' => check_plain($e->getMessage()))), 'error');
    return " ";
  }

  $xsl = new DOMDocument();
  $xsl->load($path . '/xslt/dc_to_mods.xsl');
  $input = new DOMDocument();
  $input->loadXML(trim($xmlstr));
  $xsl = $proc->importStylesheet($xsl);
  $newdom = $proc->transformToDoc($input);
  $content = $newdom->saveXML();

  return $content;
}

